namespace xecs::prefab
{
    xerr root::Serialize(xecs::serializer::stream& TextFile, bool) noexcept
    {
        xerr Error;

        Error = TextFile.Field("Guid", m_Guid.m_Instance.m_Value);

        return Error;
    }
}